module Fixer.Supply(
    Supply(),
    newSupply,
    supplyReadValues,
    supplyValue
    ) where

import Control.Monad.Trans
import Data.IORef
import Data.Typeable
import Fixer.Fixer
import qualified Data.Map as Map


-- maps b's to values of a's, creating them as needed.

data Supply b a = Supply Fixer (IORef (Map.Map b (Value a)))
    deriving(Typeable)


newSupply :: MonadIO m => Fixer -> m (Supply b a)
newSupply fixer = liftIO $ do
    ref <- newIORef Map.empty
    return $ Supply fixer ref

supplyValue :: (MonadIO m, Ord b, Fixable a) => Supply b a -> b -> m (Value a)
supplyValue (Supply fixer ref) b = liftIO $ do
    mp <- readIORef ref
    case Map.lookup b mp of
        Just v -> return v
        Nothing -> do
            v <- newValue fixer bottom
            modifyIORef ref (Map.insert b v)
            return v

supplyReadValues :: MonadIO m => Supply b a -> m [(b,a)]
supplyReadValues (Supply _fixer ref) = liftIO $ do
    mp <- readIORef ref
    flip mapM (Map.toList mp) $ \ (b,va) -> do
        a <- readValue va
        return (b,a)

